/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include <xfs.h>

/*
 * Delete record pointed to by cur/level.
 */
STATIC int					/* error */
xfs_bmbt_delrec(
	xfs_btree_cur_t		*cur,
	int			level,
	int			async,		/* deletion can be async */
	int			*stat)		/* success/failure */
{
	xfs_bmbt_block_t	*block;		/* bmap btree block */
	xfs_fsblock_t		bno;		/* fs-relative block number */
	xfs_buf_t			*bp;		/* buffer for block */
	int			error;		/* error return value */
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_delrec";
#endif
	int			i;		/* loop counter */
	int			j;		/* temp state */
	xfs_bmbt_key_t		key;		/* bmap btree key */
	xfs_bmbt_key_t		*kp;		/* pointer to bmap btree key */
	xfs_fsblock_t		lbno;		/* left sibling block number */
	xfs_buf_t			*lbp;		/* left buffer pointer */
	xfs_bmbt_block_t	*left;		/* left btree block */
	xfs_bmbt_key_t		*lkp;		/* left btree key */
	xfs_bmbt_ptr_t		*lpp;		/* left address pointer */
	int			lrecs;		/* left record count */
	xfs_bmbt_rec_t		*lrp;		/* left record pointer */
	xfs_mount_t		*mp;		/* file system mount point */
	xfs_bmbt_ptr_t		*pp;		/* pointer to bmap block addr */
	int			ptr;		/* key/record index */
	xfs_fsblock_t		rbno;		/* right sibling block number */
	xfs_buf_t			*rbp;		/* right buffer pointer */
	xfs_bmbt_block_t	*right;		/* right btree block */
	xfs_bmbt_key_t		*rkp;		/* right btree key */
	xfs_bmbt_rec_t		*rp;		/* pointer to bmap btree rec */
	xfs_bmbt_ptr_t		*rpp;		/* right address pointer */
	xfs_bmbt_block_t	*rrblock;	/* right-right btree block */
	xfs_buf_t			*rrbp;		/* right-right buffer pointer */
	int			rrecs;		/* right record count */
	xfs_bmbt_rec_t		*rrp;		/* right record pointer */
	xfs_btree_cur_t		*tcur;		/* temporary btree cursor */

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGI(cur, level);
	ptr = cur->bc_ptrs[level];
	tcur = (xfs_btree_cur_t *)0;
	if (ptr == 0) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	block = xfs_bmbt_get_block(cur, level, &bp);
#ifdef DEBUG
	if (error = xfs_btree_check_lblock(cur, block, level, bp)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		goto error0;
	}
#endif
	if (ptr > INT_GET(block->bb_numrecs, ARCH_CONVERT)) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	XFS_STATS_INC(xs_bmbt_delrec);
	if (level > 0) {
		kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
#ifdef DEBUG
		for (i = ptr; i < INT_GET(block->bb_numrecs, ARCH_CONVERT); i++) {
			if (error = xfs_btree_check_lptr(cur, INT_GET(pp[i], ARCH_CONVERT), level)) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				goto error0;
			}
		}
#endif
		if (ptr < INT_GET(block->bb_numrecs, ARCH_CONVERT)) {
			ovbcopy(&kp[ptr], &kp[ptr - 1],
				(INT_GET(block->bb_numrecs, ARCH_CONVERT) - ptr) * sizeof(*kp));
			ovbcopy(&pp[ptr], &pp[ptr - 1], /* INT_: direct copy */
				(INT_GET(block->bb_numrecs, ARCH_CONVERT) - ptr) * sizeof(*pp));
			xfs_bmbt_log_ptrs(cur, bp, ptr, INT_GET(block->bb_numrecs, ARCH_CONVERT) - 1);
			xfs_bmbt_log_keys(cur, bp, ptr, INT_GET(block->bb_numrecs, ARCH_CONVERT) - 1);
		}
	} else {
		rp = XFS_BMAP_REC_IADDR(block, 1, cur);
		if (ptr < INT_GET(block->bb_numrecs, ARCH_CONVERT)) {
			ovbcopy(&rp[ptr], &rp[ptr - 1],
				(INT_GET(block->bb_numrecs, ARCH_CONVERT) - ptr) * sizeof(*rp));
			xfs_bmbt_log_recs(cur, bp, ptr, INT_GET(block->bb_numrecs, ARCH_CONVERT) - 1);
		}
		if (ptr == 1) {
			INT_SET(key.br_startoff, ARCH_CONVERT, xfs_bmbt_get_startoff(rp));
			kp = &key;
		}
	}
	INT_MOD(block->bb_numrecs, ARCH_CONVERT, -1);
	xfs_bmbt_log_block(cur, bp, XFS_BB_NUMRECS);
	/*
	 * We're at the root level.
	 * First, shrink the root block in-memory.
	 * Try to get rid of the next level down.
	 * If we can't then there's nothing left to do.
	 */
	if (level == cur->bc_nlevels - 1) {
		xfs_iroot_realloc(cur->bc_private.b.ip, -1,
			cur->bc_private.b.whichfork);
		if (error = xfs_bmbt_killroot(cur, async)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		if (level > 0 && (error = xfs_bmbt_decrement(cur, level, &j))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 1;
		return 0;
	}
	if (ptr == 1 && (error = xfs_bmbt_updkey(cur, kp, level + 1))) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		goto error0;
	}
	if (INT_GET(block->bb_numrecs, ARCH_CONVERT) >= XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
		if (level > 0 && (error = xfs_bmbt_decrement(cur, level, &j))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 1;
		return 0;
	}
	rbno = INT_GET(block->bb_rightsib, ARCH_CONVERT);
	lbno = INT_GET(block->bb_leftsib, ARCH_CONVERT);
	/*
	 * One child of root, need to get a chance to copy its contents
	 * into the root and delete it. Can't go up to next level,
	 * there's nothing to delete there.
	 */
	if (lbno == NULLFSBLOCK && rbno == NULLFSBLOCK &&
	    level == cur->bc_nlevels - 2) {
		if (error = xfs_bmbt_killroot(cur, async)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		if (level > 0 && (error = xfs_bmbt_decrement(cur, level, &i))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 1;
		return 0;
	}
	ASSERT(rbno != NULLFSBLOCK || lbno != NULLFSBLOCK);
	if (error = xfs_btree_dup_cursor(cur, &tcur)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		goto error0;
	}
	bno = NULLFSBLOCK;
	if (rbno != NULLFSBLOCK) {
		i = xfs_btree_lastrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		if (error = xfs_bmbt_increment(tcur, level, &i)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		i = xfs_btree_lastrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		rbp = tcur->bc_bufs[level];
		right = XFS_BUF_TO_BMBT_BLOCK(rbp);
#ifdef DEBUG
		if (error = xfs_btree_check_lblock(cur, right, level, rbp)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
#endif
		bno = INT_GET(right->bb_leftsib, ARCH_CONVERT);
		if (INT_GET(right->bb_numrecs, ARCH_CONVERT) - 1 >=
		    XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if (error = xfs_bmbt_lshift(tcur, level, &i)) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				goto error0;
			}
			if (i) {
				ASSERT(INT_GET(block->bb_numrecs, ARCH_CONVERT) >=
				       XFS_BMAP_BLOCK_IMINRECS(level, tcur));
				xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
				tcur = NULL;
				if (level > 0) {
					if (error = xfs_bmbt_decrement(cur,
							level, &i)) {
						XFS_BMBT_TRACE_CURSOR(cur,
							ERROR);
						goto error0;
					}
				}
				XFS_BMBT_TRACE_CURSOR(cur, EXIT);
				*stat = 1;
				return 0;
			}
		}
		rrecs = INT_GET(right->bb_numrecs, ARCH_CONVERT);
		if (lbno != NULLFSBLOCK) {
			i = xfs_btree_firstrec(tcur, level);
			XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
			if (error = xfs_bmbt_decrement(tcur, level, &i)) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				goto error0;
			}
			XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		}
	}
	if (lbno != NULLFSBLOCK) {
		i = xfs_btree_firstrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		/*
		 * decrement to last in block
		 */
		if (error = xfs_bmbt_decrement(tcur, level, &i)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		i = xfs_btree_firstrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		lbp = tcur->bc_bufs[level];
		left = XFS_BUF_TO_BMBT_BLOCK(lbp);
#ifdef DEBUG
		if (error = xfs_btree_check_lblock(cur, left, level, lbp)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
#endif
		bno = INT_GET(left->bb_rightsib, ARCH_CONVERT);
		if (INT_GET(left->bb_numrecs, ARCH_CONVERT) - 1 >=
		    XFS_BMAP_BLOCK_IMINRECS(level, cur)) {
			if (error = xfs_bmbt_rshift(tcur, level, &i)) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				goto error0;
			}
			if (i) {
				ASSERT(INT_GET(block->bb_numrecs, ARCH_CONVERT) >=
				       XFS_BMAP_BLOCK_IMINRECS(level, tcur));
				xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
				tcur = NULL;
				if (level == 0)
					cur->bc_ptrs[0]++;
				XFS_BMBT_TRACE_CURSOR(cur, EXIT);
				*stat = 1;
				return 0;
			}
		}
		lrecs = INT_GET(left->bb_numrecs, ARCH_CONVERT);
	}
	xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
	tcur = NULL;
	mp = cur->bc_mp;
	ASSERT(bno != NULLFSBLOCK);
	if (lbno != NULLFSBLOCK &&
	    lrecs + INT_GET(block->bb_numrecs, ARCH_CONVERT) <= XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		rbno = bno;
		right = block;
		rbp = bp;
		if (error = xfs_btree_read_bufl(mp, cur->bc_tp, lbno, 0, &lbp,
				XFS_BMAP_BTREE_REF)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		left = XFS_BUF_TO_BMBT_BLOCK(lbp);
		if (error = xfs_btree_check_lblock(cur, left, level, lbp)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
	} else if (rbno != NULLFSBLOCK &&
		   rrecs + INT_GET(block->bb_numrecs, ARCH_CONVERT) <=
		   XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		lbno = bno;
		left = block;
		lbp = bp;
		if (error = xfs_btree_read_bufl(mp, cur->bc_tp, rbno, 0, &rbp,
				XFS_BMAP_BTREE_REF)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		right = XFS_BUF_TO_BMBT_BLOCK(rbp);
		if (error = xfs_btree_check_lblock(cur, right, level, rbp)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		lrecs = INT_GET(left->bb_numrecs, ARCH_CONVERT);
	} else {
		if (level > 0 && (error = xfs_bmbt_decrement(cur, level, &i))) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 1;
		return 0;
	}
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, INT_GET(left->bb_numrecs, ARCH_CONVERT) + 1, cur);
		lpp = XFS_BMAP_PTR_IADDR(left, INT_GET(left->bb_numrecs, ARCH_CONVERT) + 1, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
#ifdef DEBUG
		for (i = 0; i < INT_GET(right->bb_numrecs, ARCH_CONVERT); i++) {
			if (error = xfs_btree_check_lptr(cur, INT_GET(rpp[i], ARCH_CONVERT), level)) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				goto error0;
			}
		}
#endif
		bcopy(rkp, lkp, INT_GET(right->bb_numrecs, ARCH_CONVERT) * sizeof(*lkp));
		bcopy(rpp, lpp, INT_GET(right->bb_numrecs, ARCH_CONVERT) * sizeof(*lpp));
		xfs_bmbt_log_keys(cur, lbp, INT_GET(left->bb_numrecs, ARCH_CONVERT) + 1,
			INT_GET(left->bb_numrecs, ARCH_CONVERT) + INT_GET(right->bb_numrecs, ARCH_CONVERT));
		xfs_bmbt_log_ptrs(cur, lbp, INT_GET(left->bb_numrecs, ARCH_CONVERT) + 1,
			INT_GET(left->bb_numrecs, ARCH_CONVERT) + INT_GET(right->bb_numrecs, ARCH_CONVERT));
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, INT_GET(left->bb_numrecs, ARCH_CONVERT) + 1, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		bcopy(rrp, lrp, INT_GET(right->bb_numrecs, ARCH_CONVERT) * sizeof(*lrp));
		xfs_bmbt_log_recs(cur, lbp, INT_GET(left->bb_numrecs, ARCH_CONVERT) + 1,
			INT_GET(left->bb_numrecs, ARCH_CONVERT) + INT_GET(right->bb_numrecs, ARCH_CONVERT));
	}
	INT_MOD(left->bb_numrecs, ARCH_CONVERT, INT_GET(right->bb_numrecs, ARCH_CONVERT));
	left->bb_rightsib = right->bb_rightsib; /* INT_: direct copy */
	xfs_bmbt_log_block(cur, lbp, XFS_BB_RIGHTSIB | XFS_BB_NUMRECS);
	if (INT_GET(left->bb_rightsib, ARCH_CONVERT) != NULLDFSBNO) {
		if (error = xfs_btree_read_bufl(mp, cur->bc_tp,
				INT_GET(left->bb_rightsib, ARCH_CONVERT), 0, &rrbp,
				XFS_BMAP_BTREE_REF)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		rrblock = XFS_BUF_TO_BMBT_BLOCK(rrbp);
		if (error = xfs_btree_check_lblock(cur, rrblock, level, rrbp)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			goto error0;
		}
		INT_SET(rrblock->bb_leftsib, ARCH_CONVERT, lbno);
		xfs_bmbt_log_block(cur, rrbp, XFS_BB_LEFTSIB);
	}
	xfs_bmap_add_free(XFS_DADDR_TO_FSB(mp, XFS_BUF_ADDR(rbp)), 1,
		cur->bc_private.b.flist, mp);
	if (!async)
		xfs_trans_set_sync(cur->bc_tp);
	cur->bc_private.b.ip->i_d.di_nblocks--;
	xfs_trans_log_inode(cur->bc_tp, cur->bc_private.b.ip, XFS_ILOG_CORE);
	if (XFS_IS_QUOTA_ON(mp) &&
	    cur->bc_private.b.ip->i_ino != mp->m_sb.sb_uquotino &&
	    cur->bc_private.b.ip->i_ino != mp->m_sb.sb_gquotino)
		xfs_trans_mod_dquot_byino(cur->bc_tp, cur->bc_private.b.ip, 
			XFS_TRANS_DQ_BCOUNT, -1L);
	xfs_trans_binval(cur->bc_tp, rbp);
	if (bp != lbp) {
		cur->bc_bufs[level] = lbp;
		cur->bc_ptrs[level] += lrecs;
		cur->bc_ra[level] = 0;
	} else if (error = xfs_bmbt_increment(cur, level + 1, &i)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		goto error0;
	}
	if (level > 0)
		cur->bc_ptrs[level]--;
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	*stat = 2;
	return 0;

error0:
	if (tcur)
		xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
STATIC int					/* error */
xfs_bmbt_insrec(
	xfs_btree_cur_t		*cur,
	int			level,
	xfs_fsblock_t		*bnop,
	xfs_bmbt_rec_t		*recp,
	xfs_btree_cur_t		**curp,
	int			*stat)		/* no-go/done/continue */
{
	xfs_bmbt_block_t	*block;		/* bmap btree block */
	xfs_buf_t			*bp;		/* buffer for block */
	int			error;		/* error return value */
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_insrec";
#endif
	int			i;		/* loop index */
	xfs_bmbt_key_t		key;		/* bmap btree key */
	xfs_bmbt_key_t		*kp;		/* pointer to bmap btree key */
	int			logflags;	/* inode logging flags */
	xfs_fsblock_t		nbno;		/* new block number */
	struct xfs_btree_cur	*ncur;		/* new btree cursor */
	xfs_bmbt_key_t		nkey;		/* new btree key value */
	xfs_bmbt_rec_t		nrec;		/* new record count */
	int			optr;		/* old key/record index */
	xfs_bmbt_ptr_t		*pp;		/* pointer to bmap block addr */
	int			ptr;		/* key/record index */
	xfs_bmbt_rec_t		*rp;		/* pointer to bmap btree rec */

	ASSERT(level < cur->bc_nlevels);
	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGIFR(cur, level, *bnop, recp);
	ncur = (xfs_btree_cur_t *)0;
	INT_SET(key.br_startoff, ARCH_CONVERT, xfs_bmbt_get_startoff(recp));
	optr = ptr = cur->bc_ptrs[level];
	if (ptr == 0) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	XFS_STATS_INC(xs_bmbt_insrec);
	block = xfs_bmbt_get_block(cur, level, &bp);
#ifdef DEBUG
	if (error = xfs_btree_check_lblock(cur, block, level, bp)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
	if (ptr <= INT_GET(block->bb_numrecs, ARCH_CONVERT)) {
		if (level == 0) {
			rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
			xfs_btree_check_rec(XFS_BTNUM_BMAP, recp, rp);
		} else {
			kp = XFS_BMAP_KEY_IADDR(block, ptr, cur);
			xfs_btree_check_key(XFS_BTNUM_BMAP, &key, kp);
		}
	}
#endif
	nbno = NULLFSBLOCK;
	if (INT_GET(block->bb_numrecs, ARCH_CONVERT) == XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		if (INT_GET(block->bb_numrecs, ARCH_CONVERT) < XFS_BMAP_BLOCK_DMAXRECS(level, cur)) {
			/*
			 * A root block, that can be made bigger.
			 */
			xfs_iroot_realloc(cur->bc_private.b.ip, 1,
				cur->bc_private.b.whichfork);
			block = xfs_bmbt_get_block(cur, level, &bp);
		} else if (level == cur->bc_nlevels - 1) {
			if ((error = xfs_bmbt_newroot(cur, &logflags, stat)) ||
			    *stat == 0) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				return error;
			}
			xfs_trans_log_inode(cur->bc_tp, cur->bc_private.b.ip,
				logflags);
			block = xfs_bmbt_get_block(cur, level, &bp);
		} else {
			if (error = xfs_bmbt_rshift(cur, level, &i)) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				return error;
			}
			if (i) {
				/* nothing */
			} else {
				if (error = xfs_bmbt_lshift(cur, level, &i)) {
					XFS_BMBT_TRACE_CURSOR(cur, ERROR);
					return error;
				}
				if (i) {
					optr = ptr = cur->bc_ptrs[level];
				} else {
					if (error = xfs_bmbt_split(cur, level,
							&nbno, &nkey, &ncur,
							&i)) {
						XFS_BMBT_TRACE_CURSOR(cur,
							ERROR);
						return error;
					}
					if (i) {
						block = xfs_bmbt_get_block(
							    cur, level, &bp);
#ifdef DEBUG
						if (error =
						    xfs_btree_check_lblock(cur,
							    block, level, bp)) {
							XFS_BMBT_TRACE_CURSOR(
								cur, ERROR);
							return error;
						}
#endif
						ptr = cur->bc_ptrs[level];
						xfs_bmbt_set_allf(&nrec,
							nkey.br_startoff, 0, 0,
							XFS_EXT_NORM);
					} else {
						XFS_BMBT_TRACE_CURSOR(cur,
							EXIT);
						*stat = 0;
						return 0;
					}
				}
			}
		}
	}
	if (level > 0) {
		kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
		pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
#ifdef DEBUG
		for (i = INT_GET(block->bb_numrecs, ARCH_CONVERT); i >= ptr; i--) {
			if (error = xfs_btree_check_lptr(cur, INT_GET(pp[i - 1], ARCH_CONVERT),
					level)) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				return error;
			}
		}
#endif
		ovbcopy(&kp[ptr - 1], &kp[ptr],
			(INT_GET(block->bb_numrecs, ARCH_CONVERT) - ptr + 1) * sizeof(*kp));
		ovbcopy(&pp[ptr - 1], &pp[ptr], /* INT_: direct copy */
			(INT_GET(block->bb_numrecs, ARCH_CONVERT) - ptr + 1) * sizeof(*pp));
#ifdef DEBUG
		if (error = xfs_btree_check_lptr(cur, (xfs_bmbt_ptr_t)*bnop,
				level)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
#endif
		kp[ptr - 1] = key;
		INT_SET(pp[ptr - 1], ARCH_CONVERT, *bnop);
		INT_MOD(block->bb_numrecs, ARCH_CONVERT, +1);
		xfs_bmbt_log_keys(cur, bp, ptr, INT_GET(block->bb_numrecs, ARCH_CONVERT));
		xfs_bmbt_log_ptrs(cur, bp, ptr, INT_GET(block->bb_numrecs, ARCH_CONVERT));
	} else {
		rp = XFS_BMAP_REC_IADDR(block, 1, cur);
		ovbcopy(&rp[ptr - 1], &rp[ptr],
			(INT_GET(block->bb_numrecs, ARCH_CONVERT) - ptr + 1) * sizeof(*rp));
		rp[ptr - 1] = *recp;
		INT_MOD(block->bb_numrecs, ARCH_CONVERT, +1);
		xfs_bmbt_log_recs(cur, bp, ptr, INT_GET(block->bb_numrecs, ARCH_CONVERT));
	}
	xfs_bmbt_log_block(cur, bp, XFS_BB_NUMRECS);
#ifdef DEBUG
	if (ptr < INT_GET(block->bb_numrecs, ARCH_CONVERT)) {
		if (level == 0)
			xfs_btree_check_rec(XFS_BTNUM_BMAP, rp + ptr - 1,
				rp + ptr);
		else
			xfs_btree_check_key(XFS_BTNUM_BMAP, kp + ptr - 1,
				kp + ptr);
	}
#endif
	if (optr == 1 && (error = xfs_bmbt_updkey(cur, &key, level + 1))) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
	*bnop = nbno;
	if (nbno != NULLFSBLOCK) {
		*recp = nrec;
		*curp = ncur;
	}
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	*stat = 1;
	return 0;
}

STATIC int
xfs_bmbt_killroot(
	xfs_btree_cur_t		*cur,
	int			async)
{
	xfs_bmbt_block_t	*block;
	xfs_bmbt_block_t	*cblock;
	xfs_buf_t			*cbp;
	xfs_bmbt_key_t		*ckp;
	xfs_bmbt_ptr_t		*cpp;
#ifdef DEBUG
	int			error;
#endif
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_killroot";
#endif
	int			i;
	xfs_bmbt_key_t		*kp;
	xfs_inode_t		*ip;
	xfs_ifork_t		*ifp;
	int			level;
	xfs_bmbt_ptr_t		*pp;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	level = cur->bc_nlevels - 1;
	ASSERT(level >= 1);
	/*
	 * Don't deal with the root block needs to be a leaf case.
	 * We're just going to turn the thing back into extents anyway.
	 */
	if (level == 1) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		return 0;
	}
	block = xfs_bmbt_get_block(cur, level, &cbp);
	/*
	 * Give up if the root has multiple children.
	 */
	if (INT_GET(block->bb_numrecs, ARCH_CONVERT) != 1) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		return 0;
	}
	/*
	 * Only do this if the next level will fit.
	 * Then the data must be copied up to the inode,
	 * instead of freeing the root you free the next level.
	 */
	cbp = cur->bc_bufs[level - 1];
	cblock = XFS_BUF_TO_BMBT_BLOCK(cbp);
	if (INT_GET(cblock->bb_numrecs, ARCH_CONVERT) > XFS_BMAP_BLOCK_DMAXRECS(level, cur)) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		return 0;
	}
	ASSERT(INT_GET(cblock->bb_leftsib, ARCH_CONVERT) == NULLDFSBNO);
	ASSERT(INT_GET(cblock->bb_rightsib, ARCH_CONVERT) == NULLDFSBNO);
	ip = cur->bc_private.b.ip;
	ifp = XFS_IFORK_PTR(ip, cur->bc_private.b.whichfork);
	ASSERT(XFS_BMAP_BLOCK_IMAXRECS(level, cur) ==
	       XFS_BMAP_BROOT_MAXRECS(ifp->if_broot_bytes));
	i = (int)(INT_GET(cblock->bb_numrecs, ARCH_CONVERT) - XFS_BMAP_BLOCK_IMAXRECS(level, cur));
	if (i) {
		xfs_iroot_realloc(ip, i, cur->bc_private.b.whichfork);
		block = ifp->if_broot;
	}
	INT_MOD(block->bb_numrecs, ARCH_CONVERT, i);
	ASSERT(INT_GET(block->bb_numrecs, ARCH_CONVERT) == INT_GET(cblock->bb_numrecs, ARCH_CONVERT));
	kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
	ckp = XFS_BMAP_KEY_IADDR(cblock, 1, cur);
	bcopy(ckp, kp, INT_GET(block->bb_numrecs, ARCH_CONVERT) * sizeof(*kp));
	pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
	cpp = XFS_BMAP_PTR_IADDR(cblock, 1, cur);
#ifdef DEBUG
	for (i = 0; i < INT_GET(cblock->bb_numrecs, ARCH_CONVERT); i++) {
		if (error = xfs_btree_check_lptr(cur, INT_GET(cpp[i], ARCH_CONVERT), level - 1)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
	}
#endif
	bcopy(cpp, pp, INT_GET(block->bb_numrecs, ARCH_CONVERT) * sizeof(*pp));
	xfs_bmap_add_free(XFS_DADDR_TO_FSB(cur->bc_mp, XFS_BUF_ADDR(cbp)), 1,
		cur->bc_private.b.flist, cur->bc_mp);
	if (!async)
		xfs_trans_set_sync(cur->bc_tp);
	ip->i_d.di_nblocks--;
	if (XFS_IS_QUOTA_ON(cur->bc_mp) &&
	    ip->i_ino != cur->bc_mp->m_sb.sb_uquotino &&
	    ip->i_ino != cur->bc_mp->m_sb.sb_gquotino)
		xfs_trans_mod_dquot_byino(cur->bc_tp, ip, XFS_TRANS_DQ_BCOUNT,
			-1L);
	xfs_trans_binval(cur->bc_tp, cbp);
	cur->bc_bufs[level - 1] = NULL;
	INT_MOD(block->bb_level, ARCH_CONVERT, -1);
	xfs_trans_log_inode(cur->bc_tp, ip,
		XFS_ILOG_CORE | XFS_ILOG_FBROOT(cur->bc_private.b.whichfork));
	cur->bc_nlevels--;
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	return 0;
}

/*
 * Log key values from the btree block.
 */
STATIC void
xfs_bmbt_log_keys(
	xfs_btree_cur_t	*cur,
	xfs_buf_t		*bp,
	int		kfirst,
	int		klast)
{
#ifdef XFS_BMBT_TRACE
	static char	fname[] = "xfs_bmbt_log_keys";
#endif
	xfs_trans_t	*tp;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGBII(cur, bp, kfirst, klast);
	tp = cur->bc_tp;
	if (bp) {
		xfs_bmbt_block_t	*block;
		int			first;
		xfs_bmbt_key_t		*kp;
		int			last;

		block = XFS_BUF_TO_BMBT_BLOCK(bp);
		kp = XFS_BMAP_KEY_DADDR(block, 1, cur);
		first = (int)((xfs_caddr_t)&kp[kfirst - 1] - (xfs_caddr_t)block);
		last = (int)(((xfs_caddr_t)&kp[klast] - 1) - (xfs_caddr_t)block);
		xfs_trans_log_buf(tp, bp, first, last);
	} else {
		xfs_inode_t		 *ip;

		ip = cur->bc_private.b.ip;
		xfs_trans_log_inode(tp, ip,
			XFS_ILOG_FBROOT(cur->bc_private.b.whichfork));
	}
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
}

/*
 * Log pointer values from the btree block.
 */
STATIC void
xfs_bmbt_log_ptrs(
	xfs_btree_cur_t	*cur,
	xfs_buf_t		*bp,
	int		pfirst,
	int		plast)
{
#ifdef XFS_BMBT_TRACE
	static char	fname[] = "xfs_bmbt_log_ptrs";
#endif
	xfs_trans_t	*tp;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGBII(cur, bp, pfirst, plast);
	tp = cur->bc_tp;
	if (bp) {
		xfs_bmbt_block_t	*block;
		int			first;
		int			last;
		xfs_bmbt_ptr_t		*pp;

		block = XFS_BUF_TO_BMBT_BLOCK(bp);
		pp = XFS_BMAP_PTR_DADDR(block, 1, cur);
		first = (int)((xfs_caddr_t)&pp[pfirst - 1] - (xfs_caddr_t)block);
		last = (int)(((xfs_caddr_t)&pp[plast] - 1) - (xfs_caddr_t)block);
		xfs_trans_log_buf(tp, bp, first, last);
	} else {
		xfs_inode_t		*ip;

		ip = cur->bc_private.b.ip;
		xfs_trans_log_inode(tp, ip,
			XFS_ILOG_FBROOT(cur->bc_private.b.whichfork));
	}
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 */
STATIC int				/* error */
xfs_bmbt_lookup(
	xfs_btree_cur_t		*cur,
	xfs_lookup_t		dir,
	int			*stat)		/* success/failure */
{
	xfs_bmbt_block_t	*block;
	xfs_buf_t			*bp;
	xfs_daddr_t			d;
	xfs_sfiloff_t		diff;
	int			error;		/* error return value */
#ifdef XFS_BMBT_TRACE
	static char	fname[] = "xfs_bmbt_lookup";
#endif
	xfs_fsblock_t		fsbno;
	int			high;
	int			i;
	int			keyno;
	xfs_bmbt_key_t		*kkbase;
	xfs_bmbt_key_t		*kkp;
	xfs_bmbt_rec_t		*krbase;
	xfs_bmbt_rec_t		*krp;
	int			level;
	int			low;
	xfs_mount_t		*mp;
	xfs_bmbt_ptr_t		*pp;
	xfs_bmbt_irec_t		*rp;
	xfs_fileoff_t		startoff;
	xfs_trans_t		*tp;

	XFS_STATS_INC(xs_bmbt_lookup);
	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGI(cur, (int)dir);
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	rp = &cur->bc_rec.b;
	for (level = cur->bc_nlevels - 1, diff = 1; level >= 0; level--) {
		if (level < cur->bc_nlevels - 1) {
			d = XFS_FSB_TO_DADDR(mp, fsbno);
			bp = cur->bc_bufs[level];
			if (bp && XFS_BUF_ADDR(bp) != d)
				bp = (xfs_buf_t *)0;
			if (!bp) {
				if (error = xfs_btree_read_bufl(mp, tp, fsbno,
						0, &bp, XFS_BMAP_BTREE_REF)) {
					XFS_BMBT_TRACE_CURSOR(cur, ERROR);
					return error;
				}
				xfs_btree_setbuf(cur, level, bp);
				block = XFS_BUF_TO_BMBT_BLOCK(bp);
				if (error = xfs_btree_check_lblock(cur, block,
						level, bp)) {
					XFS_BMBT_TRACE_CURSOR(cur, ERROR);
					return error;
				}
			} else
				block = XFS_BUF_TO_BMBT_BLOCK(bp);
		} else
			block = xfs_bmbt_get_block(cur, level, &bp);
		if (diff == 0)
			keyno = 1;
		else {
			if (level > 0)
				kkbase = XFS_BMAP_KEY_IADDR(block, 1, cur);
			else
				krbase = XFS_BMAP_REC_IADDR(block, 1, cur);
			low = 1;
			if (!(high = INT_GET(block->bb_numrecs, ARCH_CONVERT))) {
				ASSERT(level == 0);
				cur->bc_ptrs[0] = dir != XFS_LOOKUP_LE;
				XFS_BMBT_TRACE_CURSOR(cur, EXIT);
				*stat = 0;
				return 0;
			}
			while (low <= high) {
				XFS_STATS_INC(xs_bmbt_compare);
				keyno = (low + high) >> 1;
				if (level > 0) {
					kkp = kkbase + keyno - 1;
					startoff = INT_GET(kkp->br_startoff, ARCH_CONVERT);
				} else {
					krp = krbase + keyno - 1;
					startoff = xfs_bmbt_get_startoff(krp);
				}
				diff = (xfs_sfiloff_t)
						(startoff - rp->br_startoff);
				if (diff < 0)
					low = keyno + 1;
				else if (diff > 0)
					high = keyno - 1;
				else
					break;
			}
		}
		if (level > 0) {
			if (diff > 0 && --keyno < 1)
				keyno = 1;
			pp = XFS_BMAP_PTR_IADDR(block, keyno, cur);
#ifdef DEBUG
			if (error = xfs_btree_check_lptr(cur, INT_GET(*pp, ARCH_CONVERT), level)) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				return error;
			}
#endif
			fsbno = INT_GET(*pp, ARCH_CONVERT);
			cur->bc_ptrs[level] = keyno;
		}
	}
	if (dir != XFS_LOOKUP_LE && diff < 0) {
		keyno++;
		/*
		 * If ge search and we went off the end of the block, but it's
		 * not the last block, we're in the wrong block.
		 */
		if (dir == XFS_LOOKUP_GE && keyno > INT_GET(block->bb_numrecs, ARCH_CONVERT) &&
		    INT_GET(block->bb_rightsib, ARCH_CONVERT) != NULLDFSBNO) {
			cur->bc_ptrs[0] = keyno;
			if (error = xfs_bmbt_increment(cur, 0, &i)) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				return error;
			}
			XFS_WANT_CORRUPTED_RETURN(i == 1);
			XFS_BMBT_TRACE_CURSOR(cur, EXIT);
			*stat = 1;
			return 0;
		}
	}
	else if (dir == XFS_LOOKUP_LE && diff > 0)
		keyno--;
	cur->bc_ptrs[0] = keyno;
	if (keyno == 0 || keyno > INT_GET(block->bb_numrecs, ARCH_CONVERT)) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
	} else {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = ((dir != XFS_LOOKUP_EQ) || (diff == 0));
	}
	return 0;
}

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int					/* error */
xfs_bmbt_lshift(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	int			error;		/* error return value */
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_lshift";
#endif
#ifdef DEBUG
	int			i;		/* loop counter */
#endif
	xfs_bmbt_key_t		key;		/* bmap btree key */
	xfs_buf_t			*lbp;		/* left buffer pointer */
	xfs_bmbt_block_t	*left;		/* left btree block */
	xfs_bmbt_key_t		*lkp;		/* left btree key */
	xfs_bmbt_ptr_t		*lpp;		/* left address pointer */
	int			lrecs;		/* left record count */
	xfs_bmbt_rec_t		*lrp;		/* left record pointer */
	xfs_mount_t		*mp;		/* file system mount point */
	xfs_buf_t			*rbp;		/* right buffer pointer */
	xfs_bmbt_block_t	*right;		/* right btree block */
	xfs_bmbt_key_t		*rkp;		/* right btree key */
	xfs_bmbt_ptr_t		*rpp;		/* right address pointer */
	xfs_bmbt_rec_t		*rrp;		/* right record pointer */

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGI(cur, level);
	if (level == cur->bc_nlevels - 1) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	rbp = cur->bc_bufs[level];
	right = XFS_BUF_TO_BMBT_BLOCK(rbp);
#ifdef DEBUG
	if (error = xfs_btree_check_lblock(cur, right, level, rbp)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
#endif
	if (INT_GET(right->bb_leftsib, ARCH_CONVERT) == NULLDFSBNO) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	if (cur->bc_ptrs[level] <= 1) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	mp = cur->bc_mp;
	if (error = xfs_btree_read_bufl(mp, cur->bc_tp, INT_GET(right->bb_leftsib, ARCH_CONVERT), 0,
			&lbp, XFS_BMAP_BTREE_REF)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
	left = XFS_BUF_TO_BMBT_BLOCK(lbp);
	if (error = xfs_btree_check_lblock(cur, left, level, lbp)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
	if (INT_GET(left->bb_numrecs, ARCH_CONVERT) == XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	lrecs = INT_GET(left->bb_numrecs, ARCH_CONVERT) + 1;
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, INT_GET(left->bb_numrecs, ARCH_CONVERT) + 1, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		*lkp = *rkp;
		xfs_bmbt_log_keys(cur, lbp, lrecs, lrecs);
		lpp = XFS_BMAP_PTR_IADDR(left, INT_GET(left->bb_numrecs, ARCH_CONVERT) + 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
#ifdef DEBUG
		if (error = xfs_btree_check_lptr(cur, INT_GET(*rpp, ARCH_CONVERT), level)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
#endif
		*lpp = *rpp; /* INT_: direct copy */
		xfs_bmbt_log_ptrs(cur, lbp, lrecs, lrecs);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, INT_GET(left->bb_numrecs, ARCH_CONVERT) + 1, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		*lrp = *rrp;
		xfs_bmbt_log_recs(cur, lbp, lrecs, lrecs);
	}
	INT_MOD(left->bb_numrecs, ARCH_CONVERT, +1);
	xfs_bmbt_log_block(cur, lbp, XFS_BB_NUMRECS);
#ifdef DEBUG
	if (level > 0)
		xfs_btree_check_key(XFS_BTNUM_BMAP, lkp - 1, lkp);
	else
		xfs_btree_check_rec(XFS_BTNUM_BMAP, lrp - 1, lrp);
#endif
	INT_MOD(right->bb_numrecs, ARCH_CONVERT, -1);
	xfs_bmbt_log_block(cur, rbp, XFS_BB_NUMRECS);
	if (level > 0) {
#ifdef DEBUG
		for (i = 0; i < INT_GET(right->bb_numrecs, ARCH_CONVERT); i++) {
			if (error = xfs_btree_check_lptr(cur, INT_GET(rpp[i + 1], ARCH_CONVERT),
					level)) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				return error;
			}
		}
#endif
		ovbcopy(rkp + 1, rkp, INT_GET(right->bb_numrecs, ARCH_CONVERT) * sizeof(*rkp));
		ovbcopy(rpp + 1, rpp, INT_GET(right->bb_numrecs, ARCH_CONVERT) * sizeof(*rpp));
		xfs_bmbt_log_keys(cur, rbp, 1, INT_GET(right->bb_numrecs, ARCH_CONVERT));
		xfs_bmbt_log_ptrs(cur, rbp, 1, INT_GET(right->bb_numrecs, ARCH_CONVERT));
	} else {
		ovbcopy(rrp + 1, rrp, INT_GET(right->bb_numrecs, ARCH_CONVERT) * sizeof(*rrp));
		xfs_bmbt_log_recs(cur, rbp, 1, INT_GET(right->bb_numrecs, ARCH_CONVERT));
		INT_SET(key.br_startoff, ARCH_CONVERT, xfs_bmbt_get_startoff(rrp));
		rkp = &key;
	}
	if (error = xfs_bmbt_updkey(cur, rkp, level + 1)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
	cur->bc_ptrs[level]--;
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	*stat = 1;
	return 0;
}

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int					/* error */
xfs_bmbt_rshift(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	int			error;		/* error return value */
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_rshift";
#endif
	int			i;		/* loop counter */
	xfs_bmbt_key_t		key;		/* bmap btree key */
	xfs_buf_t			*lbp;		/* left buffer pointer */
	xfs_bmbt_block_t	*left;		/* left btree block */
	xfs_bmbt_key_t		*lkp;		/* left btree key */
	xfs_bmbt_ptr_t		*lpp;		/* left address pointer */
	xfs_bmbt_rec_t		*lrp;		/* left record pointer */
	xfs_mount_t		*mp;		/* file system mount point */
	xfs_buf_t			*rbp;		/* right buffer pointer */
	xfs_bmbt_block_t	*right;		/* right btree block */
	xfs_bmbt_key_t		*rkp;		/* right btree key */
	xfs_bmbt_ptr_t		*rpp;		/* right address pointer */
	xfs_bmbt_rec_t		*rrp;		/* right record pointer */
	struct xfs_btree_cur	*tcur;		/* temporary btree cursor */

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGI(cur, level);
	if (level == cur->bc_nlevels - 1) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	lbp = cur->bc_bufs[level];
	left = XFS_BUF_TO_BMBT_BLOCK(lbp);
#ifdef DEBUG
	if (error = xfs_btree_check_lblock(cur, left, level, lbp)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
#endif
	if (INT_GET(left->bb_rightsib, ARCH_CONVERT) == NULLDFSBNO) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	if (cur->bc_ptrs[level] >= INT_GET(left->bb_numrecs, ARCH_CONVERT)) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	mp = cur->bc_mp;
	if (error = xfs_btree_read_bufl(mp, cur->bc_tp, INT_GET(left->bb_rightsib, ARCH_CONVERT), 0,
			&rbp, XFS_BMAP_BTREE_REF)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
	right = XFS_BUF_TO_BMBT_BLOCK(rbp);
	if (error = xfs_btree_check_lblock(cur, right, level, rbp)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
	if (INT_GET(right->bb_numrecs, ARCH_CONVERT) == XFS_BMAP_BLOCK_IMAXRECS(level, cur)) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, INT_GET(left->bb_numrecs, ARCH_CONVERT), cur);
		lpp = XFS_BMAP_PTR_IADDR(left, INT_GET(left->bb_numrecs, ARCH_CONVERT), cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
#ifdef DEBUG
		for (i = INT_GET(right->bb_numrecs, ARCH_CONVERT) - 1; i >= 0; i--) {
			if (error = xfs_btree_check_lptr(cur, INT_GET(rpp[i], ARCH_CONVERT), level)) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				return error;
			}
		}
#endif
		ovbcopy(rkp, rkp + 1, INT_GET(right->bb_numrecs, ARCH_CONVERT) * sizeof(*rkp));
		ovbcopy(rpp, rpp + 1, INT_GET(right->bb_numrecs, ARCH_CONVERT) * sizeof(*rpp));
#ifdef DEBUG
		if (error = xfs_btree_check_lptr(cur, INT_GET(*lpp, ARCH_CONVERT), level)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
#endif
		*rkp = *lkp;
		*rpp = *lpp; /* INT_: direct copy */
		xfs_bmbt_log_keys(cur, rbp, 1, INT_GET(right->bb_numrecs, ARCH_CONVERT) + 1);
		xfs_bmbt_log_ptrs(cur, rbp, 1, INT_GET(right->bb_numrecs, ARCH_CONVERT) + 1);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, INT_GET(left->bb_numrecs, ARCH_CONVERT), cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		ovbcopy(rrp, rrp + 1, INT_GET(right->bb_numrecs, ARCH_CONVERT) * sizeof(*rrp));
		*rrp = *lrp;
		xfs_bmbt_log_recs(cur, rbp, 1, INT_GET(right->bb_numrecs, ARCH_CONVERT) + 1);
		INT_SET(key.br_startoff, ARCH_CONVERT, xfs_bmbt_get_startoff(rrp));
		rkp = &key;
	}
	INT_MOD(left->bb_numrecs, ARCH_CONVERT, -1);
	xfs_bmbt_log_block(cur, lbp, XFS_BB_NUMRECS);
	INT_MOD(right->bb_numrecs, ARCH_CONVERT, +1);
#ifdef DEBUG
	if (level > 0)
		xfs_btree_check_key(XFS_BTNUM_BMAP, rkp, rkp + 1);
	else
		xfs_btree_check_rec(XFS_BTNUM_BMAP, rrp, rrp + 1);
#endif
	xfs_bmbt_log_block(cur, rbp, XFS_BB_NUMRECS);
	if (error = xfs_btree_dup_cursor(cur, &tcur)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
	i = xfs_btree_lastrec(tcur, level);
	XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
	if (error = xfs_bmbt_increment(tcur, level, &i)) {
		XFS_BMBT_TRACE_CURSOR(tcur, ERROR);
		goto error1;
	}
	XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
	if (error = xfs_bmbt_updkey(tcur, rkp, level + 1)) {
		XFS_BMBT_TRACE_CURSOR(tcur, ERROR);
		goto error1;
	}
	xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	*stat = 1;
	return 0;
error0:
	XFS_BMBT_TRACE_CURSOR(cur, ERROR);
error1:
	xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Determine the extent state.
 */
/* ARGSUSED */
STATIC xfs_exntst_t
xfs_extent_state(
	xfs_filblks_t		blks,
	int			extent_flag)
{
	if (extent_flag) {
		ASSERT(blks != 0);	/* saved for DMIG */
		return XFS_EXT_UNWRITTEN;
	}
	return XFS_EXT_NORM;
}


/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 */
STATIC int					/* error */
xfs_bmbt_split(
	xfs_btree_cur_t		*cur,
	int			level,
	xfs_fsblock_t		*bnop,
	xfs_bmbt_key_t		*keyp,
	xfs_btree_cur_t		**curp,
	int			*stat)		/* success/failure */
{
	xfs_alloc_arg_t		args;		/* block allocation args */
	int			error;		/* error return value */
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_split";
#endif
	int			i;		/* loop counter */
	xfs_fsblock_t		lbno;		/* left sibling block number */
	xfs_buf_t			*lbp;		/* left buffer pointer */
	xfs_bmbt_block_t	*left;		/* left btree block */
	xfs_bmbt_key_t		*lkp;		/* left btree key */
	xfs_bmbt_ptr_t		*lpp;		/* left address pointer */
	xfs_bmbt_rec_t		*lrp;		/* left record pointer */
	xfs_buf_t			*rbp;		/* right buffer pointer */
	xfs_bmbt_block_t	*right;		/* right btree block */
	xfs_bmbt_key_t		*rkp;		/* right btree key */
	xfs_bmbt_ptr_t		*rpp;		/* right address pointer */
	xfs_bmbt_block_t	*rrblock;	/* right-right btree block */
	xfs_buf_t			*rrbp;		/* right-right buffer pointer */
	xfs_bmbt_rec_t		*rrp;		/* right record pointer */

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGIFK(cur, level, *bnop, keyp);
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	lbp = cur->bc_bufs[level];
	lbno = XFS_DADDR_TO_FSB(args.mp, XFS_BUF_ADDR(lbp));
	left = XFS_BUF_TO_BMBT_BLOCK(lbp);
	args.fsbno = cur->bc_private.b.firstblock;
	if (args.fsbno == NULLFSBLOCK) {
		args.fsbno = lbno;
		args.type = XFS_ALLOCTYPE_START_BNO;
	} else if (cur->bc_private.b.flist->xbf_low)
		args.type = XFS_ALLOCTYPE_FIRST_AG;
	else
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
	args.mod = args.minleft = args.alignment = args.total = args.isfl =
		args.userdata = args.minalignslop = 0;
	args.minlen = args.maxlen = args.prod = 1;
	args.wasdel = cur->bc_private.b.flags & XFS_BTCUR_BPRV_WASDEL;
	if (!args.wasdel && xfs_trans_get_block_res(args.tp) == 0) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return XFS_ERROR(ENOSPC);
	}
	if (error = xfs_alloc_vextent(&args)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
	if (args.fsbno == NULLFSBLOCK) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);
	cur->bc_private.b.firstblock = args.fsbno;
	cur->bc_private.b.allocated++;
	cur->bc_private.b.ip->i_d.di_nblocks++;
	xfs_trans_log_inode(args.tp, cur->bc_private.b.ip, XFS_ILOG_CORE);
	if (XFS_IS_QUOTA_ON(args.mp) &&
	    cur->bc_private.b.ip->i_ino != args.mp->m_sb.sb_uquotino &&
	    cur->bc_private.b.ip->i_ino != args.mp->m_sb.sb_gquotino)
		xfs_trans_mod_dquot_byino(args.tp, cur->bc_private.b.ip,
			XFS_TRANS_DQ_BCOUNT, 1L);
	rbp = xfs_btree_get_bufl(args.mp, args.tp, args.fsbno, 0);
	right = XFS_BUF_TO_BMBT_BLOCK(rbp);
#ifdef DEBUG
	if (error = xfs_btree_check_lblock(cur, left, level, rbp)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
#endif
	INT_SET(right->bb_magic, ARCH_CONVERT, XFS_BMAP_MAGIC);
	right->bb_level = left->bb_level; /* INT_: direct copy */
	INT_SET(right->bb_numrecs, ARCH_CONVERT, (__uint16_t)(INT_GET(left->bb_numrecs, ARCH_CONVERT) / 2));
	if ((INT_GET(left->bb_numrecs, ARCH_CONVERT) & 1) &&
	    cur->bc_ptrs[level] <= INT_GET(right->bb_numrecs, ARCH_CONVERT) + 1)
		INT_MOD(right->bb_numrecs, ARCH_CONVERT, +1);
	i = INT_GET(left->bb_numrecs, ARCH_CONVERT) - INT_GET(right->bb_numrecs, ARCH_CONVERT) + 1;
	if (level > 0) {
		lkp = XFS_BMAP_KEY_IADDR(left, i, cur);
		lpp = XFS_BMAP_PTR_IADDR(left, i, cur);
		rkp = XFS_BMAP_KEY_IADDR(right, 1, cur);
		rpp = XFS_BMAP_PTR_IADDR(right, 1, cur);
#ifdef DEBUG
		for (i = 0; i < INT_GET(right->bb_numrecs, ARCH_CONVERT); i++) {
			if (error = xfs_btree_check_lptr(cur, INT_GET(lpp[i], ARCH_CONVERT), level)) {
				XFS_BMBT_TRACE_CURSOR(cur, ERROR);
				return error;
			}
		}
#endif
		bcopy(lkp, rkp, INT_GET(right->bb_numrecs, ARCH_CONVERT) * sizeof(*rkp));
		bcopy(lpp, rpp, INT_GET(right->bb_numrecs, ARCH_CONVERT) * sizeof(*rpp));
		xfs_bmbt_log_keys(cur, rbp, 1, INT_GET(right->bb_numrecs, ARCH_CONVERT));
		xfs_bmbt_log_ptrs(cur, rbp, 1, INT_GET(right->bb_numrecs, ARCH_CONVERT));
		keyp->br_startoff = INT_GET(rkp->br_startoff, ARCH_CONVERT);
	} else {
		lrp = XFS_BMAP_REC_IADDR(left, i, cur);
		rrp = XFS_BMAP_REC_IADDR(right, 1, cur);
		bcopy(lrp, rrp, INT_GET(right->bb_numrecs, ARCH_CONVERT) * sizeof(*rrp));
		xfs_bmbt_log_recs(cur, rbp, 1, INT_GET(right->bb_numrecs, ARCH_CONVERT));
		keyp->br_startoff = xfs_bmbt_get_startoff(rrp);
	}
	INT_MOD(left->bb_numrecs, ARCH_CONVERT, -(INT_GET(right->bb_numrecs, ARCH_CONVERT)));
	right->bb_rightsib = left->bb_rightsib; /* INT_: direct copy */
	INT_SET(left->bb_rightsib, ARCH_CONVERT, args.fsbno);
	INT_SET(right->bb_leftsib, ARCH_CONVERT, lbno);
	xfs_bmbt_log_block(cur, rbp, XFS_BB_ALL_BITS);
	xfs_bmbt_log_block(cur, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	if (INT_GET(right->bb_rightsib, ARCH_CONVERT) != NULLDFSBNO) {
		if (error = xfs_btree_read_bufl(args.mp, args.tp,
				INT_GET(right->bb_rightsib, ARCH_CONVERT), 0, &rrbp,
				XFS_BMAP_BTREE_REF)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
		rrblock = XFS_BUF_TO_BMBT_BLOCK(rrbp);
		if (error = xfs_btree_check_lblock(cur, rrblock, level, rrbp)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
		INT_SET(rrblock->bb_leftsib, ARCH_CONVERT, args.fsbno);
		xfs_bmbt_log_block(cur, rrbp, XFS_BB_LEFTSIB);
	}
	if (cur->bc_ptrs[level] > INT_GET(left->bb_numrecs, ARCH_CONVERT) + 1) {
		xfs_btree_setbuf(cur, level, rbp);
		cur->bc_ptrs[level] -= INT_GET(left->bb_numrecs, ARCH_CONVERT);
	}
	if (level + 1 < cur->bc_nlevels) {
		if (error = xfs_btree_dup_cursor(cur, curp)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
		(*curp)->bc_ptrs[level + 1]++;
	}
	*bnop = args.fsbno;
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	*stat = 1;
	return 0;
}

/*
 * Update keys for the record.
 */
STATIC int
xfs_bmbt_updkey(
	xfs_btree_cur_t		*cur,
	xfs_bmbt_key_t		*keyp,	/* on-disk format */
	int			level)
{
	xfs_bmbt_block_t	*block;
	xfs_buf_t			*bp;
#ifdef DEBUG
	int			error;
#endif
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_updkey";
#endif
	xfs_bmbt_key_t		*kp;
	int			ptr;

	ASSERT(level >= 1);
	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGIK(cur, level, keyp);
	for (ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
		block = xfs_bmbt_get_block(cur, level, &bp);
#ifdef DEBUG
		if (error = xfs_btree_check_lblock(cur, block, level, bp)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
#endif
		ptr = cur->bc_ptrs[level];
		kp = XFS_BMAP_KEY_IADDR(block, ptr, cur);
		*kp = *keyp;
		xfs_bmbt_log_keys(cur, bp, ptr, ptr);
	}
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	return 0;
}

/*
 * Convert on-disk form of btree root to in-memory form.
 */
void
xfs_bmdr_to_bmbt(
	xfs_bmdr_block_t	*dblock,
	int			dblocklen,
	xfs_bmbt_block_t	*rblock,
	int			rblocklen)
{
	int			dmxr;
	xfs_bmbt_key_t		*fkp;
	xfs_bmbt_ptr_t		*fpp;
	xfs_bmbt_key_t		*tkp;
	xfs_bmbt_ptr_t		*tpp;

	INT_SET(rblock->bb_magic, ARCH_CONVERT, XFS_BMAP_MAGIC);
	rblock->bb_level = dblock->bb_level;	/* both in on-disk format */
	ASSERT(INT_GET(rblock->bb_level, ARCH_CONVERT) > 0);
	rblock->bb_numrecs = dblock->bb_numrecs;/* both in on-disk format */
	INT_SET(rblock->bb_leftsib, ARCH_CONVERT, NULLDFSBNO);
	INT_SET(rblock->bb_rightsib, ARCH_CONVERT, NULLDFSBNO);
	dmxr = (int)XFS_BTREE_BLOCK_MAXRECS(dblocklen, xfs_bmdr, 0);
	fkp = XFS_BTREE_KEY_ADDR(dblocklen, xfs_bmdr, dblock, 1, dmxr);
	tkp = XFS_BMAP_BROOT_KEY_ADDR(rblock, 1, rblocklen);
	fpp = XFS_BTREE_PTR_ADDR(dblocklen, xfs_bmdr, dblock, 1, dmxr);
	tpp = XFS_BMAP_BROOT_PTR_ADDR(rblock, 1, rblocklen);
	dmxr = INT_GET(dblock->bb_numrecs, ARCH_CONVERT);
	bcopy(fkp, tkp, sizeof(*fkp) * dmxr);
	bcopy(fpp, tpp, sizeof(*fpp) * dmxr); /* INT_: direct copy */
}

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int						/* error */
xfs_bmbt_decrement(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	xfs_bmbt_block_t	*block;
	xfs_buf_t			*bp;
	int			error;		/* error return value */
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_decrement";
#endif
	xfs_fsblock_t		fsbno;
	int			lev;
	xfs_mount_t		*mp;
	xfs_trans_t		*tp;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGI(cur, level);
	ASSERT(level < cur->bc_nlevels);
	if (level < cur->bc_nlevels - 1)
		xfs_btree_readahead(cur, level, XFS_BTCUR_LEFTRA);
	if (--cur->bc_ptrs[level] > 0) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 1;
		return 0;
	}
	block = xfs_bmbt_get_block(cur, level, &bp);
#ifdef DEBUG
	if (error = xfs_btree_check_lblock(cur, block, level, bp)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
#endif
	if (INT_GET(block->bb_leftsib, ARCH_CONVERT) == NULLDFSBNO) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		if (--cur->bc_ptrs[lev] > 0)
			break;
		if (lev < cur->bc_nlevels - 1)
			xfs_btree_readahead(cur, lev, XFS_BTCUR_LEFTRA);
	}
	if (lev == cur->bc_nlevels) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (block = xfs_bmbt_get_block(cur, lev, &bp); lev > level; ) {
		fsbno = INT_GET(*XFS_BMAP_PTR_IADDR(block, cur->bc_ptrs[lev], cur), ARCH_CONVERT);
		if (error = xfs_btree_read_bufl(mp, tp, fsbno, 0, &bp,
				XFS_BMAP_BTREE_REF)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
		lev--;
		xfs_btree_setbuf(cur, lev, bp);
		block = XFS_BUF_TO_BMBT_BLOCK(bp);
		if (error = xfs_btree_check_lblock(cur, block, lev, bp)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
		cur->bc_ptrs[lev] = INT_GET(block->bb_numrecs, ARCH_CONVERT);
	}
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	*stat = 1;
	return 0;
}

/*
 * Delete the record pointed to by cur.
 */
int					/* error */
xfs_bmbt_delete(
	xfs_btree_cur_t	*cur,
	int		async,		/* deletion can be async */
	int		*stat)		/* success/failure */
{
	int		error;		/* error return value */
#ifdef XFS_BMBT_TRACE
	static char	fname[] = "xfs_bmbt_delete";
#endif
	int		i;
	int		level;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	for (level = 0, i = 2; i == 2; level++) {
		if (error = xfs_bmbt_delrec(cur, level, async, &i)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
	}
	if (i == 0) {
		for (level = 1; level < cur->bc_nlevels; level++) {
			if (cur->bc_ptrs[level] == 0) {
				if (error = xfs_bmbt_decrement(cur, level,
						&i)) {
					XFS_BMBT_TRACE_CURSOR(cur, ERROR);
					return error;
				}
				break;
			}
		}
	}
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	*stat = i;
	return 0;
}

/*
 * Convert a compressed bmap extent record to an uncompressed form.
 * This code must be in sync with the routines xfs_bmbt_get_startoff,
 * xfs_bmbt_get_startblock, xfs_bmbt_get_blockcount and xfs_bmbt_get_state.
 */
void
xfs_bmbt_get_all(
	xfs_bmbt_rec_t	*r,
	xfs_bmbt_irec_t	*s)
{
	int	ext_flag;
	xfs_exntst_t st;

#if BMBT_USE_64
	ext_flag = (int)((INT_GET(r->l0, ARCH_CONVERT)) >> (64 - BMBT_EXNTFLAG_BITLEN));
#if XFS_BIG_FILES
	s->br_startoff = ((xfs_fileoff_t)INT_GET(r->l0, ARCH_CONVERT) &
			   XFS_MASK64LO(64 - BMBT_EXNTFLAG_BITLEN)) >> 9;
#else	/* !XFS_BIG_FILES */
	{
		xfs_dfiloff_t	o;

		o = ((xfs_dfiloff_t)INT_GET(r->l0, ARCH_CONVERT) &
		      XFS_MASK64LO(64 - BMBT_EXNTFLAG_BITLEN)) >> 9;
		ASSERT((o >> 32) == 0);
		s->br_startoff = (xfs_fileoff_t)o;
	}
#endif	/* XFS_BIG_FILES */
#if XFS_BIG_FILESYSTEMS
	s->br_startblock = (((xfs_fsblock_t)INT_GET(r->l0, ARCH_CONVERT) & XFS_MASK64LO(9)) << 43) | 
			   (((xfs_fsblock_t)INT_GET(r->l1, ARCH_CONVERT)) >> 21);
#else
#ifdef DEBUG
	{
		xfs_dfsbno_t	b;

		b = (((xfs_dfsbno_t)INT_GET(r->l0, ARCH_CONVERT) & XFS_MASK64LO(9)) << 43) | 
		    (((xfs_dfsbno_t)INT_GET(r->l1, ARCH_CONVERT)) >> 21);
		ASSERT((b >> 32) == 0 || ISNULLDSTARTBLOCK(b));
		s->br_startblock = (xfs_fsblock_t)b;
	}
#else	/* !DEBUG */
	s->br_startblock = (xfs_fsblock_t)(((xfs_dfsbno_t)INT_GET(r->l1, ARCH_CONVERT)) >> 21);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILESYSTEMS */
	s->br_blockcount = (xfs_filblks_t)(INT_GET(r->l1, ARCH_CONVERT) & XFS_MASK64LO(21));
#else	/* !BMBT_USE_64 */
	ext_flag = (INT_GET(r->l0, ARCH_CONVERT) >> (32 - BMBT_EXNTFLAG_BITLEN));
#if XFS_BIG_FILES
	s->br_startoff = (((xfs_fileoff_t)INT_GET(r->l0, ARCH_CONVERT) &
			    XFS_MASK32LO(32 - BMBT_EXNTFLAG_BITLEN)) << 23) |
			 (((xfs_fileoff_t)INT_GET(r->l1, ARCH_CONVERT)) >> 9);
#else	/* !XFS_BIG_FILES */
#ifdef DEBUG
	{
		xfs_dfiloff_t	o;

		o = (((xfs_dfiloff_t)INT_GET(r->l0, ARCH_CONVERT) &
		       XFS_MASK32LO(32 - BMBT_EXNTFLAG_BITLEN)) << 23) |
		    (((xfs_dfiloff_t)INT_GET(r->l1, ARCH_CONVERT)) >> 9);
		ASSERT((o >> 32) == 0);
		s->br_startoff = (xfs_fileoff_t)o;
	}
#else	/* !DEBUG */
	s->br_startoff = (((xfs_fileoff_t)INT_GET(r->l0, ARCH_CONVERT) &
			    XFS_MASK32LO(32 - BMBT_EXNTFLAG_BITLEN)) << 23) |
			 (((xfs_fileoff_t)INT_GET(r->l1, ARCH_CONVERT)) >> 9);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILES */
#if XFS_BIG_FILESYSTEMS
	s->br_startblock =
		(((xfs_fsblock_t)(INT_GET(r->l1, ARCH_CONVERT) & XFS_MASK32LO(9))) << 43) | 
		(((xfs_fsblock_t)INT_GET(r->l2, ARCH_CONVERT)) << 11) |
		(((xfs_fsblock_t)INT_GET(r->l3, ARCH_CONVERT)) >> 21);
#else
#ifdef DEBUG
	{
		xfs_dfsbno_t	b;

		b = (((xfs_dfsbno_t)(INT_GET(r->l1, ARCH_CONVERT) & XFS_MASK32LO(9))) << 43) | 
		    (((xfs_dfsbno_t)INT_GET(r->l2, ARCH_CONVERT)) << 11) |
		    (((xfs_dfsbno_t)INT_GET(r->l3, ARCH_CONVERT)) >> 21);
		ASSERT((b >> 32) == 0 || ISNULLDSTARTBLOCK(b));
		s->br_startblock = (xfs_fsblock_t)b;
	}
#else	/* !DEBUG */
	s->br_startblock = (((xfs_fsblock_t)INT_GET(r->l2, ARCH_CONVERT)) << 11) |
			   (((xfs_fsblock_t)INT_GET(r->l3, ARCH_CONVERT)) >> 21);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILESYSTEMS */
	s->br_blockcount = (xfs_filblks_t)(INT_GET(r->l3, ARCH_CONVERT) & XFS_MASK32LO(21));
#endif	/* BMBT_USE_64 */
	/* This is xfs_extent_state() in-line */
	if (ext_flag) {
		ASSERT(s->br_blockcount != 0);	/* saved for DMIG */
		st = XFS_EXT_UNWRITTEN;
	} else
		st = XFS_EXT_NORM;
	s->br_state = st;
}

/*
 * Get the block pointer for the given level of the cursor.
 * Fill in the buffer pointer, if applicable.
 */
xfs_bmbt_block_t *
xfs_bmbt_get_block(
	xfs_btree_cur_t		*cur,
	int			level,
	xfs_buf_t			**bpp)
{
	xfs_ifork_t		*ifp;
	xfs_bmbt_block_t	*rval;

	if (level < cur->bc_nlevels - 1) {
		*bpp = cur->bc_bufs[level];
		rval = XFS_BUF_TO_BMBT_BLOCK(*bpp);
	} else {
		*bpp = 0;
		ifp = XFS_IFORK_PTR(cur->bc_private.b.ip,
			cur->bc_private.b.whichfork);
		rval = ifp->if_broot;
	}
	return rval;
}

/*
 * Extract the blockcount field from a bmap extent record.
 */
xfs_filblks_t
xfs_bmbt_get_blockcount(
	xfs_bmbt_rec_t	*r)
{
#if BMBT_USE_64
	return (xfs_filblks_t)(INT_GET(r->l1, ARCH_CONVERT) & XFS_MASK64LO(21));
#else	/* !BMBT_USE_64 */
	return (xfs_filblks_t)(INT_GET(r->l3, ARCH_CONVERT) & XFS_MASK32LO(21));
#endif	/* BMBT_USE_64 */
}

/*
 * Extract the startblock field from a bmap extent record.
 */
xfs_fsblock_t
xfs_bmbt_get_startblock(
	xfs_bmbt_rec_t	*r)
{
#if BMBT_USE_64
#if XFS_BIG_FILESYSTEMS
	return (((xfs_fsblock_t)INT_GET(r->l0, ARCH_CONVERT) & XFS_MASK64LO(9)) << 43) |
	       (((xfs_fsblock_t)INT_GET(r->l1, ARCH_CONVERT)) >> 21);
#else
#ifdef DEBUG
	xfs_dfsbno_t	b;

	b = (((xfs_dfsbno_t)INT_GET(r->l0, ARCH_CONVERT) & XFS_MASK64LO(9)) << 43) |
	    (((xfs_dfsbno_t)INT_GET(r->l1, ARCH_CONVERT)) >> 21);
	ASSERT((b >> 32) == 0 || ISNULLDSTARTBLOCK(b));
	return (xfs_fsblock_t)b;
#else	/* !DEBUG */
	return (xfs_fsblock_t)(((xfs_dfsbno_t)INT_GET(r->l1, ARCH_CONVERT)) >> 21);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILESYSTEMS */
#else	/* !BMBT_USE_64 */
#if XFS_BIG_FILESYSTEMS
	return (((xfs_fsblock_t)(INT_GET(r->l1, ARCH_CONVERT) & XFS_MASK32LO(9))) << 43) | 
	       (((xfs_fsblock_t)INT_GET(r->l2, ARCH_CONVERT)) << 11) |
	       (((xfs_fsblock_t)INT_GET(r->l3, ARCH_CONVERT)) >> 21);
#else
#ifdef DEBUG
	xfs_dfsbno_t	b;

	b = (((xfs_dfsbno_t)(INT_GET(r->l1, ARCH_CONVERT) & XFS_MASK32LO(9))) << 43) | 
	    (((xfs_dfsbno_t)INT_GET(r->l2, ARCH_CONVERT)) << 11) |
	    (((xfs_dfsbno_t)INT_GET(r->l3, ARCH_CONVERT)) >> 21);
	ASSERT((b >> 32) == 0 || ISNULLDSTARTBLOCK(b));
	return (xfs_fsblock_t)b;
#else	/* !DEBUG */
	return (((xfs_fsblock_t)INT_GET(r->l2, ARCH_CONVERT)) << 11) |
	       (((xfs_fsblock_t)INT_GET(r->l3, ARCH_CONVERT)) >> 21);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILESYSTEMS */
#endif	/* BMBT_USE_64 */
}

/*
 * Extract the startoff field from a bmap extent record.
 */
xfs_fileoff_t
xfs_bmbt_get_startoff(
	xfs_bmbt_rec_t	*r)
{
#if BMBT_USE_64
#if XFS_BIG_FILES
	return ((xfs_fileoff_t)INT_GET(r->l0, ARCH_CONVERT) &
		 XFS_MASK64LO(64 - BMBT_EXNTFLAG_BITLEN)) >> 9;
#else	/* !XFS_BIG_FILES */
	xfs_dfiloff_t	o;

	o = ((xfs_dfiloff_t)INT_GET(r->l0, ARCH_CONVERT) &
	      XFS_MASK64LO(64 - BMBT_EXNTFLAG_BITLEN)) >> 9;
	ASSERT((o >> 32) == 0);
	return (xfs_fileoff_t)o;
#endif	/* XFS_BIG_FILES */
#else	/* !BMBT_USE_64 */
#if XFS_BIG_FILES
	return (((xfs_fileoff_t)INT_GET(r->l0, ARCH_CONVERT) &
		  XFS_MASK32LO(32 - BMBT_EXNTFLAG_BITLEN)) << 23) |
	       (((xfs_fileoff_t)INT_GET(r->l1, ARCH_CONVERT)) >> 9);
#else	/* !XFS_BIG_FILES */
#ifdef DEBUG
	xfs_dfiloff_t	o;

	o = (((xfs_dfiloff_t)INT_GET(r->l0, ARCH_CONVERT) &
	       XFS_MASK32LO(32 - BMBT_EXNTFLAG_BITLEN)) << 23) |
	    (((xfs_dfiloff_t)INT_GET(r->l1, ARCH_CONVERT)) >> 9);
	ASSERT((o >> 32) == 0);
	return (xfs_fileoff_t)o;
#else	/* !DEBUG */
	return (((xfs_fileoff_t)INT_GET(r->l0, ARCH_CONVERT) &
		  XFS_MASK32LO(32 - BMBT_EXNTFLAG_BITLEN)) << 23) |
	       (((xfs_fileoff_t)INT_GET(r->l1, ARCH_CONVERT)) >> 9);
#endif	/* DEBUG */
#endif	/* XFS_BIG_FILES */
#endif	/* BMBT_USE_64 */
}

xfs_exntst_t
xfs_bmbt_get_state(
	xfs_bmbt_rec_t  *r)
{
	int	ext_flag;

#if BMBT_USE_64
	ext_flag = (int)((INT_GET(r->l0, ARCH_CONVERT)) >> (64 - BMBT_EXNTFLAG_BITLEN));
#else	/* !BMBT_USE_64 */
	ext_flag = (INT_GET(r->l0, ARCH_CONVERT) >> (32 - BMBT_EXNTFLAG_BITLEN));
#endif	/* BMBT_USE_64 */
	return xfs_extent_state(xfs_bmbt_get_blockcount(r),
				ext_flag);
}


/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int						/* error */
xfs_bmbt_increment(
	xfs_btree_cur_t		*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	xfs_bmbt_block_t	*block;
	xfs_buf_t			*bp;
	int			error;		/* error return value */
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_increment";
#endif
	xfs_fsblock_t		fsbno;
	int			lev;
	xfs_mount_t		*mp;
	xfs_trans_t		*tp;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGI(cur, level);
	ASSERT(level < cur->bc_nlevels);
	if (level < cur->bc_nlevels - 1)
		xfs_btree_readahead(cur, level, XFS_BTCUR_RIGHTRA);
	block = xfs_bmbt_get_block(cur, level, &bp);
#ifdef DEBUG
	if (error = xfs_btree_check_lblock(cur, block, level, bp)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
#endif
	if (++cur->bc_ptrs[level] <= INT_GET(block->bb_numrecs, ARCH_CONVERT)) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 1;
		return 0;
	}
	if (INT_GET(block->bb_rightsib, ARCH_CONVERT) == NULLDFSBNO) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		block = xfs_bmbt_get_block(cur, lev, &bp);
#ifdef DEBUG
		if (error = xfs_btree_check_lblock(cur, block, lev, bp)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
#endif
		if (++cur->bc_ptrs[lev] <= INT_GET(block->bb_numrecs, ARCH_CONVERT))
			break;
		if (lev < cur->bc_nlevels - 1)
			xfs_btree_readahead(cur, lev, XFS_BTCUR_RIGHTRA);
	}
	if (lev == cur->bc_nlevels) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	tp = cur->bc_tp;
	mp = cur->bc_mp;
	for (block = xfs_bmbt_get_block(cur, lev, &bp); lev > level; ) {
		fsbno = INT_GET(*XFS_BMAP_PTR_IADDR(block, cur->bc_ptrs[lev], cur), ARCH_CONVERT);
		if (error = xfs_btree_read_bufl(mp, tp, fsbno, 0, &bp,
				XFS_BMAP_BTREE_REF)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
		lev--;
		xfs_btree_setbuf(cur, lev, bp);
		block = XFS_BUF_TO_BMBT_BLOCK(bp);
		if (error = xfs_btree_check_lblock(cur, block, lev, bp)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
		cur->bc_ptrs[lev] = 1;
	}
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	*stat = 1;
	return 0;
}

/*
 * Insert the current record at the point referenced by cur.
 */
int					/* error */
xfs_bmbt_insert(
	xfs_btree_cur_t	*cur,
	int		*stat)		/* success/failure */
{
	int		error;		/* error return value */
#ifdef XFS_BMBT_TRACE
	static char	fname[] = "xfs_bmbt_insert";
#endif
	int		i;
	int		level;
	xfs_fsblock_t	nbno;
	xfs_btree_cur_t	*ncur;
	xfs_bmbt_rec_t	nrec;
	xfs_btree_cur_t	*pcur;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	level = 0;
	nbno = NULLFSBLOCK;
	xfs_bmbt_set_all(&nrec, &cur->bc_rec.b);
	ncur = (xfs_btree_cur_t *)0;
	pcur = cur;
	do {
		if (error = xfs_bmbt_insrec(pcur, level++, &nbno, &nrec, &ncur,
				&i)) {
			if (pcur != cur)
				xfs_btree_del_cursor(pcur, XFS_BTREE_ERROR);
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		if (pcur != cur && (ncur || nbno == NULLFSBLOCK)) {
			cur->bc_nlevels = pcur->bc_nlevels;
			cur->bc_private.b.allocated +=
				pcur->bc_private.b.allocated;
			pcur->bc_private.b.allocated = 0;
			ASSERT((cur->bc_private.b.firstblock != NULLFSBLOCK) ||
			       (cur->bc_private.b.ip->i_d.di_flags & 
				XFS_DIFLAG_REALTIME));
			cur->bc_private.b.firstblock =
				pcur->bc_private.b.firstblock;
			ASSERT(cur->bc_private.b.flist ==
			       pcur->bc_private.b.flist);
			xfs_btree_del_cursor(pcur, XFS_BTREE_NOERROR);
		}
		if (ncur) {
			pcur = ncur;
			ncur = (xfs_btree_cur_t *)0;
		}
	} while (nbno != NULLFSBLOCK);
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	*stat = i;
	return 0;
error0:
	XFS_BMBT_TRACE_CURSOR(cur, ERROR);
	return error;
}

/*
 * Log fields from the btree block header.
 */
void
xfs_bmbt_log_block(
	xfs_btree_cur_t		*cur,
	xfs_buf_t			*bp,
	int			fields)
{
	int			first;
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_log_block";
#endif
	int			last;
	xfs_trans_t		*tp;
	static const short	offsets[] = {
		offsetof(xfs_bmbt_block_t, bb_magic),
		offsetof(xfs_bmbt_block_t, bb_level),
		offsetof(xfs_bmbt_block_t, bb_numrecs),
		offsetof(xfs_bmbt_block_t, bb_leftsib),
		offsetof(xfs_bmbt_block_t, bb_rightsib),
		sizeof(xfs_bmbt_block_t)
	};

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGBI(cur, bp, fields);
	tp = cur->bc_tp;
	if (bp) {
		xfs_btree_offsets(fields, offsets, XFS_BB_NUM_BITS, &first,
				  &last);
		xfs_trans_log_buf(tp, bp, first, last);
	} else
		xfs_trans_log_inode(tp, cur->bc_private.b.ip,
			XFS_ILOG_FBROOT(cur->bc_private.b.whichfork));
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
}

/*
 * Log record values from the btree block.
 */
void
xfs_bmbt_log_recs(
	xfs_btree_cur_t		*cur,
	xfs_buf_t			*bp,
	int			rfirst,
	int			rlast)
{
	xfs_bmbt_block_t	*block;
	int			first;
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_log_recs";
#endif
	int			last;
	xfs_bmbt_rec_t		*rp;
	xfs_trans_t		*tp;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGBII(cur, bp, rfirst, rlast);
	ASSERT(bp);
	tp = cur->bc_tp;
	block = XFS_BUF_TO_BMBT_BLOCK(bp);
	rp = XFS_BMAP_REC_DADDR(block, 1, cur);
	first = (int)((xfs_caddr_t)&rp[rfirst - 1] - (xfs_caddr_t)block);
	last = (int)(((xfs_caddr_t)&rp[rlast] - 1) - (xfs_caddr_t)block);
	xfs_trans_log_buf(tp, bp, first, last);
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
}

int					/* error */
xfs_bmbt_lookup_eq(
	xfs_btree_cur_t	*cur,
	xfs_fileoff_t	off,
	xfs_fsblock_t	bno,
	xfs_filblks_t	len,
	int		*stat)		/* success/failure */
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_EQ, stat);
}

int					/* error */
xfs_bmbt_lookup_ge(
	xfs_btree_cur_t	*cur,
	xfs_fileoff_t	off,
	xfs_fsblock_t	bno,
	xfs_filblks_t	len,
	int		*stat)		/* success/failure */
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_GE, stat);
}

int					/* error */
xfs_bmbt_lookup_le(
	xfs_btree_cur_t	*cur,
	xfs_fileoff_t	off,
	xfs_fsblock_t	bno,
	xfs_filblks_t	len,
	int		*stat)		/* success/failure */
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_bmbt_lookup(cur, XFS_LOOKUP_LE, stat);
}

/*
 * Give the bmap btree a new root block.  Copy the old broot contents
 * down into a real block and make the broot point to it.
 */
int						/* error */
xfs_bmbt_newroot(
	xfs_btree_cur_t		*cur,		/* btree cursor */
	int			*logflags,	/* logging flags for inode */
	int			*stat)		/* return status - 0 fail */
{
	xfs_alloc_arg_t		args;		/* allocation arguments */
	xfs_bmbt_block_t	*block;		/* bmap btree block */
	xfs_buf_t			*bp;		/* buffer for block */
	xfs_bmbt_block_t	*cblock;	/* child btree block */
	xfs_bmbt_key_t		*ckp;		/* child key pointer */
	xfs_bmbt_ptr_t		*cpp;		/* child ptr pointer */
	int			error;		/* error return code */
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_newroot";
#endif
#ifdef DEBUG
	int			i;		/* loop counter */
#endif
	xfs_bmbt_key_t		*kp;		/* pointer to bmap btree key */
	int			level;		/* btree level */
	xfs_bmbt_ptr_t		*pp;		/* pointer to bmap block addr */

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	level = cur->bc_nlevels - 1;
	block = xfs_bmbt_get_block(cur, level, &bp);
	/*
	 * Copy the root into a real block.
	 */
	args.mp = cur->bc_mp;
	pp = XFS_BMAP_PTR_IADDR(block, 1, cur);
	args.tp = cur->bc_tp;
	args.fsbno = cur->bc_private.b.firstblock;
	args.mod = args.minleft = args.alignment = args.total = args.isfl =
		args.userdata = args.minalignslop = 0;
	args.minlen = args.maxlen = args.prod = 1;
	args.wasdel = cur->bc_private.b.flags & XFS_BTCUR_BPRV_WASDEL;
	if (args.fsbno == NULLFSBLOCK) {
#ifdef DEBUG
		if (error = xfs_btree_check_lptr(cur, INT_GET(*pp, ARCH_CONVERT), level)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
#endif
		args.fsbno = INT_GET(*pp, ARCH_CONVERT);
		args.type = XFS_ALLOCTYPE_START_BNO;
	} else if (args.wasdel)
		args.type = XFS_ALLOCTYPE_FIRST_AG;
	else
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
	if (error = xfs_alloc_vextent(&args)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
	if (args.fsbno == NULLFSBLOCK) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);
	cur->bc_private.b.firstblock = args.fsbno;
	cur->bc_private.b.allocated++;
	cur->bc_private.b.ip->i_d.di_nblocks++;
	if (XFS_IS_QUOTA_ON(args.mp) &&
	    cur->bc_private.b.ip->i_ino != args.mp->m_sb.sb_uquotino &&
	    cur->bc_private.b.ip->i_ino != args.mp->m_sb.sb_gquotino)
		xfs_trans_mod_dquot_byino(args.tp, cur->bc_private.b.ip,
					  XFS_TRANS_DQ_BCOUNT, 1L);
	bp = xfs_btree_get_bufl(args.mp, cur->bc_tp, args.fsbno, 0);
	cblock = XFS_BUF_TO_BMBT_BLOCK(bp);
	*cblock = *block;
	INT_MOD(block->bb_level, ARCH_CONVERT, +1);
	INT_SET(block->bb_numrecs, ARCH_CONVERT, 1);
	cur->bc_nlevels++;
	cur->bc_ptrs[level + 1] = 1;
	kp = XFS_BMAP_KEY_IADDR(block, 1, cur);
	ckp = XFS_BMAP_KEY_IADDR(cblock, 1, cur);
	bcopy(kp, ckp, INT_GET(cblock->bb_numrecs, ARCH_CONVERT) * sizeof(*kp));
	cpp = XFS_BMAP_PTR_IADDR(cblock, 1, cur);
#ifdef DEBUG
	for (i = 0; i < INT_GET(cblock->bb_numrecs, ARCH_CONVERT); i++) {
		if (error = xfs_btree_check_lptr(cur, INT_GET(pp[i], ARCH_CONVERT), level)) {
			XFS_BMBT_TRACE_CURSOR(cur, ERROR);
			return error;
		}
	}
#endif
	bcopy(pp, cpp, INT_GET(cblock->bb_numrecs, ARCH_CONVERT) * sizeof(*pp));
#ifdef DEBUG
	if (error = xfs_btree_check_lptr(cur, (xfs_bmbt_ptr_t)args.fsbno,
			level)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
#endif
	INT_SET(*pp, ARCH_CONVERT, args.fsbno);
	xfs_iroot_realloc(cur->bc_private.b.ip, 1 - INT_GET(cblock->bb_numrecs, ARCH_CONVERT),
		cur->bc_private.b.whichfork);
	xfs_btree_setbuf(cur, level, bp);
	/*
	 * Do all this logging at the end so that 
	 * the root is at the right level.
	 */
	xfs_bmbt_log_block(cur, bp, XFS_BB_ALL_BITS);
	xfs_bmbt_log_keys(cur, bp, 1, INT_GET(cblock->bb_numrecs, ARCH_CONVERT));
	xfs_bmbt_log_ptrs(cur, bp, 1, INT_GET(cblock->bb_numrecs, ARCH_CONVERT));
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	*logflags |=
		XFS_ILOG_CORE | XFS_ILOG_FBROOT(cur->bc_private.b.whichfork);
	*stat = 1;
	return 0;
}

/*
 * Set all the fields in a bmap extent record from the uncompressed form.
 */
void
xfs_bmbt_set_all(
	xfs_bmbt_rec_t	*r,
	xfs_bmbt_irec_t	*s)
{
	int	extent_flag;

	ASSERT((s->br_state == XFS_EXT_NORM) ||
		(s->br_state == XFS_EXT_UNWRITTEN));
	extent_flag = (s->br_state == XFS_EXT_NORM) ? 0 : 1;
#if XFS_BIG_FILES
	ASSERT((s->br_startoff & XFS_MASK64HI(9)) == 0);
	ASSERT((s->br_blockcount & XFS_MASK64HI(43)) == 0);
#else	/* !XFS_BIG_FILES */
	ASSERT((s->br_blockcount & XFS_MASK32HI(11)) == 0);
#endif	/* XFS_BIG_FILES */
#if XFS_BIG_FILESYSTEMS
	ASSERT((s->br_startblock & XFS_MASK64HI(12)) == 0);
#endif	/* XFS_BIG_FILESYSTEMS */
#if BMBT_USE_64
#if XFS_BIG_FILESYSTEMS
	INT_SET(r->l0, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)extent_flag << 63) | 
		  ((xfs_bmbt_rec_base_t)s->br_startoff << 9) |
		  ((xfs_bmbt_rec_base_t)s->br_startblock >> 43));
	INT_SET(r->l1, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)s->br_startblock << 21) | 
		  ((xfs_bmbt_rec_base_t)s->br_blockcount &
		   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)));
#else	/* !XFS_BIG_FILESYSTEMS */
	if (ISNULLSTARTBLOCK(s->br_startblock)) {
		INT_SET(r->l0, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)extent_flag << 63) |
			((xfs_bmbt_rec_base_t)s->br_startoff << 9) |
			  (xfs_bmbt_rec_base_t)XFS_MASK64LO(9));
		INT_SET(r->l1, ARCH_CONVERT, XFS_MASK64HI(11) |
			  ((xfs_bmbt_rec_base_t)s->br_startblock << 21) |
			  ((xfs_bmbt_rec_base_t)s->br_blockcount &
			   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)));
	} else {
		INT_SET(r->l0, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)extent_flag << 63) |
			((xfs_bmbt_rec_base_t)s->br_startoff << 9));
		INT_SET(r->l1, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)s->br_startblock << 21) | 
			  ((xfs_bmbt_rec_base_t)s->br_blockcount &
			   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)));
	}
#endif	/* XFS_BIG_FILESYSTEMS */
#else	/* !BMBT_USE_64 */
	INT_SET(r->l0, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)extent_flag << 31) |
		((xfs_bmbt_rec_base_t)(s->br_startoff >> 23)));
	INT_SET(r->l3, ARCH_CONVERT, (((xfs_bmbt_rec_base_t)s->br_startblock) << 21) |
		  ((xfs_bmbt_rec_base_t)(s->br_blockcount & XFS_MASK32LO(21))));
#if XFS_BIG_FILESYSTEMS
	INT_SET(r->l1, ARCH_CONVERT, (((xfs_bmbt_rec_base_t)s->br_startoff) << 9) |
		  ((xfs_bmbt_rec_base_t)(s->br_startblock >> 43)));
	INT_SET(r->l2, ARCH_CONVERT, (xfs_bmbt_rec_base_t)(s->br_startblock >> 11));
#else	/* !XFS_BIG_FILESYSTEMS */
	if (ISNULLSTARTBLOCK(s->br_startblock)) {
		INT_SET(r->l1, ARCH_CONVERT, (xfs_bmbt_rec_base_t)(s->br_startoff << 9) |
			  (xfs_bmbt_rec_base_t)XFS_MASK32LO(9));
		INT_SET(r->l2, ARCH_CONVERT, (xfs_bmbt_rec_base_t)XFS_MASK32HI(11) |
			  (xfs_bmbt_rec_base_t)(s->br_startblock >> 11));
	} else {
		INT_SET(r->l1, ARCH_CONVERT, (xfs_bmbt_rec_base_t)(s->br_startoff << 9));
		INT_SET(r->l2, ARCH_CONVERT, (xfs_bmbt_rec_base_t)(s->br_startblock >> 11));
	}
#endif	/* XFS_BIG_FILESYSTEMS */
#endif	/* BMBT_USE_64 */
}

/*
 * Set all the fields in a bmap extent record from the arguments.
 */
void
xfs_bmbt_set_allf(
	xfs_bmbt_rec_t	*r,
	xfs_fileoff_t	o,
	xfs_fsblock_t	b,
	xfs_filblks_t	c,
	xfs_exntst_t	v)
{
	int	extent_flag;

	ASSERT((v == XFS_EXT_NORM) || (v == XFS_EXT_UNWRITTEN));
	extent_flag = (v == XFS_EXT_NORM) ? 0 : 1;
#if XFS_BIG_FILES
	ASSERT((o & XFS_MASK64HI(64-BMBT_STARTOFF_BITLEN)) == 0);
	ASSERT((c & XFS_MASK64HI(64-BMBT_BLOCKCOUNT_BITLEN)) == 0);
#else	/* !XFS_BIG_FILES */
	ASSERT((c & XFS_MASK32HI(11)) == 0);
#endif	/* XFS_BIG_FILES */
#if XFS_BIG_FILESYSTEMS
	ASSERT((b & XFS_MASK64HI(64-BMBT_STARTBLOCK_BITLEN)) == 0);
#endif	/* XFS_BIG_FILESYSTEMS */
#if BMBT_USE_64
#if XFS_BIG_FILESYSTEMS
	INT_SET(r->l0, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)extent_flag << 63) | 
	        ((xfs_bmbt_rec_base_t)o << 9) |
		((xfs_bmbt_rec_base_t)b >> 43));
	INT_SET(r->l1, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)b << 21) | 
		  ((xfs_bmbt_rec_base_t)c &
		   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)));
#else	/* !XFS_BIG_FILESYSTEMS */
	if (ISNULLSTARTBLOCK(b)) {
		INT_SET(r->l0, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)extent_flag << 63) |
			((xfs_bmbt_rec_base_t)o << 9) |
			 (xfs_bmbt_rec_base_t)XFS_MASK64LO(9));
		INT_SET(r->l1, ARCH_CONVERT, XFS_MASK64HI(11) |
			  ((xfs_bmbt_rec_base_t)b << 21) |
			  ((xfs_bmbt_rec_base_t)c &
			   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)));
	} else {
		INT_SET(r->l0, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)extent_flag << 63) |
			((xfs_bmbt_rec_base_t)o << 9));
		INT_SET(r->l1, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)b << 21) | 
			  ((xfs_bmbt_rec_base_t)c &
			   (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)));
	}
#endif	/* XFS_BIG_FILESYSTEMS */
#else	/* !BMBT_USE_64 */
	INT_SET(r->l0, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)extent_flag << 31) |
		((xfs_bmbt_rec_base_t)(o >> 23)));
	INT_SET(r->l3, ARCH_CONVERT, (((xfs_bmbt_rec_base_t)b) << 21) |
		  ((xfs_bmbt_rec_base_t)(c & XFS_MASK32LO(21))));
#if XFS_BIG_FILESYSTEMS
	INT_SET(r->l1, ARCH_CONVERT, (((xfs_bmbt_rec_base_t)o) << 9) |
		  ((xfs_bmbt_rec_base_t)(b >> 43)));
	INT_SET(r->l2, ARCH_CONVERT, (xfs_bmbt_rec_base_t)(b >> 11));
#else	/* !XFS_BIG_FILESYSTEMS */
	if (ISNULLSTARTBLOCK(b)) {
		INT_SET(r->l1, ARCH_CONVERT, (xfs_bmbt_rec_base_t)(o << 9) |
			  (xfs_bmbt_rec_base_t)XFS_MASK32LO(9));
		INT_SET(r->l2, ARCH_CONVERT, (xfs_bmbt_rec_base_t)XFS_MASK32HI(11) |
			  (xfs_bmbt_rec_base_t)(b >> 11));
	} else {
		INT_SET(r->l1, ARCH_CONVERT, (xfs_bmbt_rec_base_t)(o << 9));
		INT_SET(r->l2, ARCH_CONVERT, (xfs_bmbt_rec_base_t)(b >> 11));
	}
#endif	/* XFS_BIG_FILESYSTEMS */
#endif	/* BMBT_USE_64 */
}

/*
 * Set the blockcount field in a bmap extent record.
 */
void
xfs_bmbt_set_blockcount(
	xfs_bmbt_rec_t	*r,
	xfs_filblks_t	v)
{
#if XFS_BIG_FILES
	ASSERT((v & XFS_MASK64HI(43)) == 0);
#else	/* !XFS_BIG_FILES */
	ASSERT((v & XFS_MASK32HI(11)) == 0);
#endif
#if BMBT_USE_64
	INT_SET(r->l1, ARCH_CONVERT, (INT_GET(r->l1, ARCH_CONVERT) & (xfs_bmbt_rec_base_t)XFS_MASK64HI(43)) |
		  (xfs_bmbt_rec_base_t)(v & XFS_MASK64LO(21)));
#else	/* !BMBT_USE_64 */
	INT_SET(r->l3, ARCH_CONVERT, (INT_GET(r->l3, ARCH_CONVERT) & (xfs_bmbt_rec_base_t)XFS_MASK32HI(11)) |
		  ((xfs_bmbt_rec_base_t)v & XFS_MASK32LO(21)));
#endif	/* BMBT_USE_64 */
}

/*
 * Set the startblock field in a bmap extent record.
 */
void
xfs_bmbt_set_startblock(
	xfs_bmbt_rec_t	*r,
	xfs_fsblock_t	v)
{
#if XFS_BIG_FILESYSTEMS
	ASSERT((v & XFS_MASK64HI(12)) == 0);
#endif	/* XFS_BIG_FILESYSTEMS */
#if BMBT_USE_64
#if XFS_BIG_FILESYSTEMS
	INT_SET(r->l0, ARCH_CONVERT, (INT_GET(r->l0, ARCH_CONVERT) & (xfs_bmbt_rec_base_t)XFS_MASK64HI(55)) |
		  (xfs_bmbt_rec_base_t)(v >> 43));
	INT_SET(r->l1, ARCH_CONVERT, (INT_GET(r->l1, ARCH_CONVERT) & (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)) |
		  (xfs_bmbt_rec_base_t)(v << 21));
#else	/* !XFS_BIG_FILESYSTEMS */
	if (ISNULLSTARTBLOCK(v)) {
		INT_SET(r->l0, ARCH_CONVERT, (INT_GET(r->l0, ARCH_CONVERT) | (xfs_bmbt_rec_base_t)XFS_MASK64LO(9)));
		INT_SET(r->l1, ARCH_CONVERT, (xfs_bmbt_rec_base_t)XFS_MASK64HI(11) |
			  ((xfs_bmbt_rec_base_t)v << 21) |
			  (INT_GET(r->l1, ARCH_CONVERT) & (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)));
	} else {
		INT_SET(r->l0, ARCH_CONVERT, (INT_GET(r->l0, ARCH_CONVERT) & ~(xfs_bmbt_rec_base_t)XFS_MASK64LO(9)));
		INT_SET(r->l1, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)v << 21) |
			  (INT_GET(r->l1, ARCH_CONVERT) & (xfs_bmbt_rec_base_t)XFS_MASK64LO(21)));
	}
#endif	/* XFS_BIG_FILESYSTEMS */
#else	/* !BMBT_USE_64 */
#if XFS_BIG_FILESYSTEMS
	INT_SET(r->l1, ARCH_CONVERT, (INT_GET(r->l1, ARCH_CONVERT) & XFS_MASK32HI(23)) | (xfs_bmbt_rec_base_t)(v >> 43));
	INT_SET(r->l2, ARCH_CONVERT, (xfs_bmbt_rec_base_t)(v >> 11));
#else	/* !XFS_BIG_FILESYSTEMS */
	if (ISNULLSTARTBLOCK(v)) {
		INT_SET(r->l1, ARCH_CONVERT, (INT_GET(r->l1, ARCH_CONVERT) | XFS_MASK32LO(9)));
		INT_SET(r->l2, ARCH_CONVERT, XFS_MASK32HI(11) | (xfs_bmbt_rec_base_t)(v >> 11));
	} else {
		INT_SET(r->l1, ARCH_CONVERT, (INT_GET(r->l1, ARCH_CONVERT) & ~XFS_MASK32LO(9)));
		INT_SET(r->l2, ARCH_CONVERT, (xfs_bmbt_rec_base_t)(v >> 11));
	}
#endif	/* XFS_BIG_FILESYSTEMS */
	INT_SET(r->l3, ARCH_CONVERT, (INT_GET(r->l3, ARCH_CONVERT) & XFS_MASK32LO(21)) |
		  (((xfs_bmbt_rec_base_t)v) << 21));
#endif	/* BMBT_USE_64 */
}

/*
 * Set the startoff field in a bmap extent record.
 */
void
xfs_bmbt_set_startoff(
	xfs_bmbt_rec_t	*r,
	xfs_fileoff_t	v)
{
#if XFS_BIG_FILES
	ASSERT((v & XFS_MASK64HI(9)) == 0);
#endif	/* XFS_BIG_FILES */
#if BMBT_USE_64
	INT_SET(r->l0, ARCH_CONVERT, (INT_GET(r->l0, ARCH_CONVERT) & (xfs_bmbt_rec_base_t) XFS_MASK64HI(1)) |
		((xfs_bmbt_rec_base_t)v << 9) |
		  (INT_GET(r->l0, ARCH_CONVERT) & (xfs_bmbt_rec_base_t)XFS_MASK64LO(9)));
#else	/* !BMBT_USE_64 */
	INT_SET(r->l0, ARCH_CONVERT, (INT_GET(r->l0, ARCH_CONVERT) & (xfs_bmbt_rec_base_t) XFS_MASK32HI(1)) |
		(xfs_bmbt_rec_base_t)(v >> 23));
	INT_SET(r->l1, ARCH_CONVERT, ((xfs_bmbt_rec_base_t)v << 9) |
		  (INT_GET(r->l1, ARCH_CONVERT) & (xfs_bmbt_rec_base_t)XFS_MASK32LO(9)));
#endif	/* BMBT_USE_64 */
}

/*
 * Set the extent state field in a bmap extent record.
 */
void
xfs_bmbt_set_state(
	xfs_bmbt_rec_t	*r,
	xfs_exntst_t	v)
{
	ASSERT(v == XFS_EXT_NORM || v == XFS_EXT_UNWRITTEN);
	if (v == XFS_EXT_NORM)
#if BMBT_USE_64
		INT_SET(r->l0, ARCH_CONVERT, INT_GET(r->l0, ARCH_CONVERT) & XFS_MASK64LO(64 - BMBT_EXNTFLAG_BITLEN));
#else	/* !BMBT_USE_64 */
		INT_SET(r->l0, ARCH_CONVERT, INT_GET(r->l0, ARCH_CONVERT) & XFS_MASK32LO(32 - BMBT_EXNTFLAG_BITLEN));
#endif	/* BMBT_USE_64 */
	else
#if BMBT_USE_64
		INT_SET(r->l0, ARCH_CONVERT, INT_GET(r->l0, ARCH_CONVERT) | XFS_MASK64HI(BMBT_EXNTFLAG_BITLEN));
#else	/* !BMBT_USE_64 */
		INT_SET(r->l0, ARCH_CONVERT, INT_GET(r->l0, ARCH_CONVERT) | XFS_MASK32HI(BMBT_EXNTFLAG_BITLEN));
#endif	/* BMBT_USE_64 */
}

/*
 * Convert in-memory form of btree root to on-disk form.
 */
void
xfs_bmbt_to_bmdr(
	xfs_bmbt_block_t	*rblock,
	int			rblocklen,
	xfs_bmdr_block_t	*dblock,
	int			dblocklen)
{
	int			dmxr;
	xfs_bmbt_key_t		*fkp;
	xfs_bmbt_ptr_t		*fpp;
	xfs_bmbt_key_t		*tkp;
	xfs_bmbt_ptr_t		*tpp;

	ASSERT(INT_GET(rblock->bb_magic, ARCH_CONVERT) == XFS_BMAP_MAGIC);
	ASSERT(INT_GET(rblock->bb_leftsib, ARCH_CONVERT) == NULLDFSBNO);
	ASSERT(INT_GET(rblock->bb_rightsib, ARCH_CONVERT) == NULLDFSBNO);
	ASSERT(INT_GET(rblock->bb_level, ARCH_CONVERT) > 0);
	dblock->bb_level = rblock->bb_level;	/* both in on-disk format */
	dblock->bb_numrecs = rblock->bb_numrecs;/* both in on-disk format */
	dmxr = (int)XFS_BTREE_BLOCK_MAXRECS(dblocklen, xfs_bmdr, 0);
	fkp = XFS_BMAP_BROOT_KEY_ADDR(rblock, 1, rblocklen);
	tkp = XFS_BTREE_KEY_ADDR(dblocklen, xfs_bmdr, dblock, 1, dmxr);
	fpp = XFS_BMAP_BROOT_PTR_ADDR(rblock, 1, rblocklen);
	tpp = XFS_BTREE_PTR_ADDR(dblocklen, xfs_bmdr, dblock, 1, dmxr);
	dmxr = INT_GET(dblock->bb_numrecs, ARCH_CONVERT);
	bcopy(fkp, tkp, sizeof(*fkp) * dmxr);
	bcopy(fpp, tpp, sizeof(*fpp) * dmxr); /* INT_: direct copy */
}

/*
 * Update the record to the passed values.
 */
int
xfs_bmbt_update(
	xfs_btree_cur_t		*cur,
	xfs_fileoff_t		off,
	xfs_fsblock_t		bno,
	xfs_filblks_t		len,
	xfs_exntst_t		state)
{
	xfs_bmbt_block_t	*block;
	xfs_buf_t			*bp;
	int			error;
#ifdef XFS_BMBT_TRACE
	static char		fname[] = "xfs_bmbt_update";
#endif
	xfs_bmbt_key_t		key;
	int			ptr;
	xfs_bmbt_rec_t		*rp;

	XFS_BMBT_TRACE_CURSOR(cur, ENTRY);
	XFS_BMBT_TRACE_ARGFFFI(cur, (xfs_dfiloff_t)off, (xfs_dfsbno_t)bno,
		(xfs_dfilblks_t)len, (int)state);
	block = xfs_bmbt_get_block(cur, 0, &bp);
#ifdef DEBUG
	if (error = xfs_btree_check_lblock(cur, block, 0, bp)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
#endif
	ptr = cur->bc_ptrs[0];
	rp = XFS_BMAP_REC_IADDR(block, ptr, cur);
	xfs_bmbt_set_allf(rp, off, bno, len, state);
	xfs_bmbt_log_recs(cur, bp, ptr, ptr);
	if (ptr > 1) {
		XFS_BMBT_TRACE_CURSOR(cur, EXIT);
		return 0;
	}
	INT_SET(key.br_startoff, ARCH_CONVERT, off);
	if (error = xfs_bmbt_updkey(cur, &key, 1)) {
		XFS_BMBT_TRACE_CURSOR(cur, ERROR);
		return error;
	}
	XFS_BMBT_TRACE_CURSOR(cur, EXIT);
	return 0;
}

/*
 * Check an extent list, which has just been read, for
 * any bit in the extent flag field. ASSERT on debug
 * kernels, as this condition should not occur.
 * Return an error condition (1) if any flags found,
 * otherwise return 0.
 */
int
xfs_check_nostate_extents(
	xfs_bmbt_rec_t		*ep,
	xfs_extnum_t		num)
{
	for (; num > 0; num--, ep++) {
		if (
#if BMBT_USE_64
			((INT_GET(ep->l0, ARCH_CONVERT)) >> (64 - BMBT_EXNTFLAG_BITLEN)) != 0
#else	/* !BMBT_USE_64 */
			((INT_GET(ep->l0, ARCH_CONVERT)) >> (32 - BMBT_EXNTFLAG_BITLEN)) != 0
#endif	/* BMBT_USE_64 */
		) {
			ASSERT(0);
			return 1;
		}
	}
	return 0;
}
